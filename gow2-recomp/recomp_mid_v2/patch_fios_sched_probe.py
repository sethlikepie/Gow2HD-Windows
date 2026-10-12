#!/usr/bin/env python3
"""Probe gated do PRODUTOR de `[op+0x90]` (a palavra de conclusao das ops FIOS).

PORQUE
------
`func_002B4224` (o poll do estado 1 do player da intro) le `[op+0x90]` e so'
devolve "done" quando essa palavra e' != 0. Quem a POE nao esta' no caminho do
open nem no do poll -- esta' noutra thread. Cadeia estatica, verificada no lift:

    func_0030B058(r3=mediaobj, r4=op)          <- submissao
      [op+0x1C] |= 0x20
      push lock-free de `op` na lista [mediaobj+0x204]  (lwarx/stwcx)
      func_00315514([mediaobj+0xC8])   lock
      func_003153AC([mediaobj+0xF0])   signal
      func_0031545C([mediaobj+0xC8])   unlock

    thread "fios scheduler" (sys_ppu_thread_create, prio 100)
      entry OPD 0x00534370 -> func_00314E2C (trampolim generico:
        chama [[arg+0x14]] com r3=arg) -> OPD 0x005341A8 -> func_0030ED68
      func_0030ED68 -> func_0030EDD0 : ciclo do escalonador; espera em
        func_003157A4([mediaobj+0xF0]) e drena a lista +0x204/+0x20C/+0x234

    func_0030644C(r3=op, r4=estado, r5=...)    <- conclusao da op
      lock [ [op+0x94] + 0x60 ]
      se (r4 & 0xFF) == 1 -> func_00306534:
            r9 = op ; lwzu r0,0x90(r9) ; stw r11,0(r9)   ==>  [op+0x90] = 1
      senao -> unlock + callback [op+0x14] com r3=[op+0x18]

Ou seja: `[op+0x90] = 1` sai de `func_00306534`, chamado por `func_0030644C`
com estado 1, e no caminho de sucesso quem chama `func_0030644C` e' o ciclo do
escalonador (`func_0030EDD0`). No caminho de erro e' `func_0030AFD0`.

Esta probe mede, IN-BOOT, qual desses tres factos acontece:
  [FIOSSCHED] sched   -> o corpo do escalonador correu de todo
  [FIOSSCHED] complete-> func_0030644C foi chamada (com que estado, em que op)
  [FIOSSCHED] done90  -> [op+0x90] foi mesmo escrito (o produtor disparou)

Gated por PS3_TRACE_FIOSSCHED=1, OFF por default. Estritamente read-only.
Idempotente. Marcador: FIOS-SCHED-PROBE.
"""
from pathlib import Path
import sys

MARKER = "FIOS-SCHED-PROBE"
ENV = "PS3_TRACE_FIOSSCHED"

ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

GATE = (
    "        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
    '            const char* _e=getenv("' + ENV + '"); _on=(_e&&*_e&&*_e!=\'0\')?1:0;}\n'
)


def probe(tag, body):
    return ("        /* " + MARKER + "(" + tag + ") */\n" + GATE + body)


# --- corpo do escalonador FIOS (a thread que devia completar as ops) --------
SCHED_PROBE = probe("sched",
    "          if(_on){ static int _n=0; if(_n++<4){\n"
    '            fprintf(stderr,"[FIOSSCHED] sched #%d entrou func_0030ED68 r3=0x%08X\\n",\n'
    "              _n,(uint32_t)ctx->gpr[3]);\n"
    "            fflush(stderr); } } }\n")

# --- conclusao da op: r3=op, r4&0xFF=estado --------------------------------
COMPL_PROBE = probe("complete",
    "          if(_on){ static int _n=0; if(_n++<24){\n"
    "            uint32_t _op=(uint32_t)ctx->gpr[3];\n"
    '            fprintf(stderr,"[FIOSSCHED] complete #%d op=0x%08X estado=%u opcode=%u erro=0x%08X done_antes=0x%08X\\n",\n'
    "              _n,_op,(unsigned)(ctx->gpr[4]&0xFFu),\n"
    "              _op?vm_read32(_op+0x40):0u,_op?vm_read32(_op+0x44):0u,\n"
    "              _op?vm_read32(_op+0x90):0u);\n"
    "            fflush(stderr); } } }\n")

# --- a escrita em si: func_00306534 e' o unico sitio que poe [op+0x90] -----
DONE_PROBE = probe("done90",
    "          if(_on){ static int _n=0; if(_n++<24){\n"
    "            uint32_t _op=(uint32_t)ctx->gpr[31];\n"
    '            fprintf(stderr,"[FIOSSCHED] done90 #%d op=0x%08X <- %u (opcode=%u)\\n",\n'
    "              _n,_op,(unsigned)(ctx->gpr[11]&0xFFFFFFFFu),\n"
    "              _op?vm_read32(_op+0x40):0u);\n"
    "            fflush(stderr); } } }\n")

SITES = [
    ("func_0030ED68", "sched",    SCHED_PROBE),
    ("func_0030644C", "complete", COMPL_PROBE),
    ("func_00306534", "done90",   DONE_PROBE),
]


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    orig = t
    for fn, tag, body in SITES:
        stamp = MARKER + "(" + tag + ")"
        if stamp in t:
            continue
        sig = "void " + fn + "(ppu_context* ctx) {\n"
        if sig not in t:
            continue
        i = t.index(sig) + len(sig)
        head, tail = t[:i], t[i:]
        t = head + body + tail
    if t == orig:
        return "ALREADY" if MARKER in orig else "SKIP"
    p.write_text(t, encoding="utf-8", newline="\n")
    return "APPLIED"


def main() -> int:
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    seen = False
    for p in files:
        r = patch_file(p)
        if r != "SKIP":
            print("%s: %s" % (p.name, r))
        if r in ("APPLIED", "ALREADY"):
            seen = True
    if not seen:
        print("nenhum sitio encontrado -- shape do lift mudou?")
        return 1
    print("probe %s pronta -- corra com %s=1" % (MARKER, ENV))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
