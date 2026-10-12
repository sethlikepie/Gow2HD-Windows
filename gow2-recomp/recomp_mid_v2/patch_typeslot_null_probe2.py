#!/usr/bin/env python3
"""Acrescenta `rec=` (o ponteiro de registo) a sonda TYPESLOT.

Porque
------
A sonda V1 (`patch_typeslot_null_probe.py`) mediu, determinista em 3/3:

    [TYPESLOT] NULO idx=0x25F7C tipo=0x97DF tab=0x00868D48 slot=0x00000000

Lido o corpo de `func_002545D4` inteiro, o `tipo` vem de `*(uint16*)(rec + 2)`,
e o `rec` e' montado assim, por no' da lista:

    r10 = *(no + 0x8)            // payload
    r31 = *(no + 0x0)            // next
    r11 = 0
    if (r10 == 0) goto usa_r11   // <- fica 0
    r11 = r10 + 4
    usa_r11:
      r4 = r11 ; tipo = *(uint16*)(r11 + 2) ; idx = (tipo<<2)&0x3FFFC
      obj = tab[idx] ; vt = *(obj) ; opd = *(vt+0x18) ; bctrl

Ou seja: se o payload do no' for NULO, o codigo le' o "tipo" do endereco
guest 0x2 e despacha na mesma. `0x97DF` sendo identico em 3/3 corridas e'
consistente com uma leitura de memoria fixa (a base do espaco guest), nao com
conteudo de WAD -- mas isso e' INFERENCIA.

Esta sonda decide-o: imprime `rec` (que ainda esta em r4 no ponto da sonda).
`rec=0x00000000` confirma que o defeito e' um no' com payload nulo na lista, e
nao um tipo mal lido de um registo valido.

Uso:  patch_typeslot_null_probe2.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha da V1 nao existe.
"""
import os
import sys
import glob

MARKER = "TYPESLOT-REC"

NEEDLE = (
    "              fprintf(stderr,\"[TYPESLOT] %s idx=0x%X tipo=0x%X tab=0x%08X slot=0x%08X\\n\",\n"
    "                _slot?\"ok  \":\"NULO\", _idx, _idx>>2, (uint32_t)ctx->gpr[29], _slot);\n"
)

REPL = (
    "              /* " + MARKER + ": r4 ainda tem o ponteiro de registo de onde saiu o tipo */\n"
    "              fprintf(stderr,\"[TYPESLOT] %s idx=0x%X tipo=0x%X tab=0x%08X slot=0x%08X rec=0x%08X\\n\",\n"
    "                _slot?\"ok  \":\"NULO\", _idx, _idx>>2, (uint32_t)ctx->gpr[29], _slot,\n"
    "                (uint32_t)ctx->gpr[4]);\n"
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
        print("MISSING  agulha da sonda TYPESLOT (V1) nao existe -- correr o V1 primeiro",
              file=sys.stderr)
        return 2
    print("patch_typeslot_null_probe2: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
