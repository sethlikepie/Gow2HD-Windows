#!/usr/bin/env python3
"""Sonda da entrada de `func_00146CB8` -- o criador da thread AUTO_LOAD.

Cadeia estatica, verificada contra o EBOOT.ELF (2026-08-01)
-----------------------------------------------------------
    OPD 0x00521768 -> fn 0x00147038                 corpo da thread AUTO_LOAD
    ponteiro para o OPD em 0x0053D0F4 = TOC-0x4084
    func_00146CB8 le' TOC-0x4084 (entry) + TOC-0x4080 (nome), prio=0x3E9,
      stack=0x4000, e chama o wrapper func_004B9A38 (sys_ppu_thread_create)

    chamadores de func_00146CB8 (bl, do binario):  0x000BB1E4 e 0x000BBD70
    o OPD proprio dela (0x00521740) nao e' referenciado por ninguem -- morto.

Medido: `sys_ppu_thread_create name="AUTO_LOAD"` **nunca acontece**. As threads
criadas sao BPETrophyInitThread, fios mediathread, fios scheduler,
snd_stream_service_thread e syn_tick_timer_thread.

Sobre o sitio 0x000BBD70 (o que fica em func_000BBB00) ja' se sabe que esta
morto: a sonda `PS3_TRACE_ALGATE` no teste que o guarda deu **zero** numa
corrida valida (R_PermA completo, 20298800), e o fragmento que desemboca la'
(`func_000BB9EC`, 0x000BB9EC) **nao tem um unico ramo nem ponteiro** a apontar
para ele em todo o EBOOT. Note-se que NAO e' um fallthrough perdido pelo
lifter: em 0x000BB9E8 ha' um `b 0x000BB6C8` explicito -- verificado
desmontando o binario, hipotese de bug do lifter REFUTADA.

Sobra o sitio 0x000BB1E4 (em func_000BB19C).

O que esta sonda responde
-------------------------
Distingue os dois estados que faltam, e que so' se separam com medicao:

  - a sonda nao imprime nada  -> nenhum dos dois chamadores corre; a parede
    esta a montante deles, e o wrapper de criacao e' inocente.
  - a sonda imprime           -> o criador CORRE e mesmo assim nao ha' thread;
    o defeito esta entre aqui e o sys_ppu_thread_create (o wrapper
    func_004B9A38, ou o retorno dele).

Gate: `PS3_TRACE_ALCREATE` (vazio ou "0" = OFF, default). Sem cap fixo:
`PS3_TRACE_ALCREATE_CAP` (default 40, `-1` = ilimitado). Read-only.

Uso:  patch_146cb8_autoload_creator_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "ALCREATE-PROBE"

NEEDLE = (
    "        ctx->gpr[0] = (int64_t)(int32_t)(1);\n"
    "        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x4084);\n"
    "        ctx->gpr[5] = ctx->gpr[3] | ctx->gpr[3];\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": o criador da AUTO_LOAD chegou a correr? */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_ALCREATE\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_ALCREATE_CAP\"); _cap=(_c&&*_c)?atoi(_c):40; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _opd=(uint32_t)ctx->gpr[4];\n"
    "            fprintf(stderr,\"[ALCREATE] #%d chamado-de lr=0x%08X this=0x%08X \"\n"
    "              \"opd=0x%08X fn=0x%08X nome_ea=0x%08X\\n\",\n"
    "              _n,(uint32_t)ctx->lr,(uint32_t)ctx->gpr[11],_opd,\n"
    "              (_opd>=0x10000u&&_opd<0x4F000000u)?vm_read32(_opd):0u,\n"
    "              (uint32_t)vm_read32(ctx->gpr[2] + -0x4080));\n"
    "            fflush(stderr); } } }\n"
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
        print("MISSING  agulha do criador da AUTO_LOAD nao encontrada", file=sys.stderr)
        return 2
    print("patch_146cb8_autoload_creator_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
