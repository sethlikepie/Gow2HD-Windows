#!/usr/bin/env python3
"""Sonda do teste que decide se a thread AUTO_LOAD chega a ser criada.

Como se chegou aqui (tudo medido a 2026-08-01)
----------------------------------------------
O gate da cadeia dizia "parado no AUTO_LOAD" porque procurava
`thr_auto_load() end`, string que nao existe em binario nenhum. Com o marcador
real de fim de thread (`[SYS] sys_ppu_thread end`) e com o `lr` do guest no de
criacao, mediu-se o que de facto acontece:

    threads criadas:  BPETrophyInitThread, fios mediathread, fios scheduler,
                      snd_stream_service_thread, syn_tick_timer_thread
    AUTO_LOAD:        NUNCA CRIADA (zero chamadas a sys_ppu_thread_create)

Logo a pergunta nao e' "porque nao termina" -- e' "porque nunca e' criada".

Cadeia estatica ate' ao sitio (verificada contra o EBOOT.ELF)
-------------------------------------------------------------
    OPD 0x00521768 -> fn 0x00147038          (o corpo da thread AUTO_LOAD)
    ponteiro para esse OPD guardado em 0x0053D0F4 = TOC-0x4084
    func_00146CB8 le' TOC-0x4084 (entry) e TOC-0x4080 (o nome "AUTO_LOAD"),
      prio=0x3E9, stack=0x4000, e chama o wrapper func_004B9A38
    func_00146CB8 e' chamada de dois sitios: lr=0x000BBD74 e lr=0x000BB1E8

E o sitio 0x000BBD74 esta atras deste teste:

    r29 = *(TOC-0x60DC)              // base do array
    r10 = *(TOC-0x60C4)
    r9  = r9 + r29                   // elemento
    r11 = *(r10 + 0)                 // valor esperado
    r0  = *(r9 + 0x3C4)              // valor actual
    if (r0 != r11) -> func_000BB6C4  // desvia, NAO cria a thread
    ...                              // senao: inicializa e cria AUTO_LOAD

## O que esta sonda mede

Os dois lados da comparacao, o elemento e a base, e se o salto foi tomado.
Responde de uma vez a duas perguntas que so' se distinguem com medicao:

  - o codigo chega sequer aqui?  (se a sonda nao imprime nada, a parede esta
    mais a montante e este teste e' inocente)
  - se chega, quais sao os dois valores?  (nomeia o estado que falta)

Gate: `PS3_TRACE_ALGATE` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_ALGATE_CAP` (default 40, `-1` = sem limite -- esta sonda nasce sem
cap fixo, pela mesma razao que as outras tres de hoje: um cap hard-coded ja'
escondeu a amostra que interessava tres vezes nesta sessao).

A agulha repete-se em 6 fragmentos do mesmo codigo guest; aplica-se a todos --
read-only, nao muda nenhum registo nem nenhuma memoria.

Uso:  patch_bbd74_autoload_gate_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARKER = "ALGATE-PROBE"

NEEDLE = (
    "        ctx->gpr[11] = vm_read32(ctx->gpr[10] + 0x0);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x3C4);\n"
    "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int32_t)ctx->gpr[11]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_000BB6C4; return; }\n"
)

REPL = (
    "        ctx->gpr[11] = vm_read32(ctx->gpr[10] + 0x0);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x3C4);\n"
    "        /* " + MARKER + ": o teste que decide se a AUTO_LOAD e' criada */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_ALGATE\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_ALGATE_CAP\"); _cap=(_c&&*_c)?atoi(_c):40; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _cur=(uint32_t)ctx->gpr[0], _esp=(uint32_t)ctx->gpr[11];\n"
    "            fprintf(stderr,\"[ALGATE] #%d elem=0x%08X +3C4=0x%08X esperado=0x%08X -> %s\\n\",\n"
    "              _n,(uint32_t)ctx->gpr[9],_cur,_esp,\n"
    "              (_cur==_esp)?\"CRIA AUTO_LOAD\":\"desvia (nao cria)\");\n"
    "            fflush(stderr); } } }\n"
    "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int32_t)ctx->gpr[11]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_000BB6C4; return; }\n"
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
        print("MISSING  agulha do teste do AUTO_LOAD nao encontrada", file=sys.stderr)
        return 2
    print("patch_bbd74_autoload_gate_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
