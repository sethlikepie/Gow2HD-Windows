#!/usr/bin/env python3
"""DESTROY -- quem destroi os objectos cujo handler faz POP do escopo (E87).

Medido (E86/E87): nos 10 gestores que chegam ao #042 em repouso ha' 2 POPs em
lr=0x002B9CA8 (destrutor por tipo FUN_002b9c80 -> handler +8 -> FUN_002b0d60,
que faz pop no gestor de obj+2) contra 1 PUSH em lr=0x002B0DDC (FUN_002b0db0,
push no gestor de obj+6). Nos 5 gestores que mantem o escopo nao ha nada disto.
Esta sonda imprime, na ENTRADA de cada uma das tres funcoes, o objecto, os
campos +0/+2/+6 e os ra do host -- para emparelhar cada pop com o seu push (ou
provar que nao tem) e para nomear quem manda destruir.

Captura, nao recalcula: so' le' campos do objecto que a propria funcao vai ler
a seguir. Gate PS3_TRACE_DESTROY (OFF por default). Sem cap.
"""
import glob, os, sys
MARKER = "DESTROY-PROBE"
def bloco(tag):
    return ('        /* ' + MARKER + ' ' + tag + ' */\n'
            '        { static int _d_on = -1; if (_d_on < 0) { extern char* getenv(const char*);\n'
            '              const char* _e = getenv("PS3_TRACE_DESTROY"); _d_on = (_e && *_e && *_e != \'0\') ? 1 : 0; }\n'
            '          if (_d_on) { const char* ps3_dbg_sym(void*);\n'
            '            uint32_t _o = (uint32_t)ctx->gpr[3];\n'
            '            fprintf(stderr, "[DESTROY] ' + tag + ' obj=0x%08X r4=0x%08X w0=0x%08X +2=0x%04X +6=0x%04X lr=0x%08X ra1=%s ra2=%s\\n",\n'
            '                _o, (uint32_t)ctx->gpr[4], vm_read32(_o), vm_read16(_o + 2), vm_read16(_o + 6), (uint32_t)ctx->lr,\n'
            '                ps3_dbg_sym(__builtin_return_address(0)), ps3_dbg_sym(__builtin_return_address(1))); fflush(stderr); } }\n')
SITES = {"func_002B9C80": "destrutor", "func_002B0D60": "handler-POP", "func_002B0DB0": "PUSH"}
def main():
    lift = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "recomp_macos_v2"))
    n = 0
    for path in sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp"))):
        s = open(path, errors="replace").read()
        if MARKER in s: print("ALREADY", os.path.basename(path)); return 0
        t = s
        for fn, tag in SITES.items():
            h = "void %s(ppu_context* ctx) {\n" % fn
            if t.count(h) == 1: t = t.replace(h, h + bloco(tag + " " + fn), 1); n += 1
        if t != s: open(path, "w").write(t); print("APPLIED", os.path.basename(path))
    print("sitios:", n); return 0 if n == 3 else 2
if __name__ == "__main__": sys.exit(main())
