#!/usr/bin/env python3
"""E86 -- DIAGNOSTICO gated: saltar o pop-de-tudo no fim de FUN_00041494 (#038 do B71).

NAO E' FIX. No console esse pop corre. E' um discriminador: muda UMA variavel -- o
estado do escopo dos 15 gestores durante os passos #042-#054 do B71 -- e mede o
efeito no frame. Predicoes commitadas ANTES em ps3recomp 412935b (E86).

Porque este sitio: medido no E85, o ultimo evento de escopo antes do #042 e' o
POP com lr=0x000415A4 em cada uma das 15 fabricas -- o vt[0x44] do gestor +0x58
que segue `bl 0xC2E68` em FUN_00041494. Depois dele, cursor=-1 em todas, e os
passos #042-#054 fazem vt[0x4c] (le' mgr+0x44, um inteiro, como ponteiro) e
vt[0x48] (devolve 0), o que no console seria crash. Logo no console ha' escopo
aberto ai'. Este gate poe-nos nesse estado por forca bruta para ver o que muda.

Gate: PS3_E86_KEEP_SCOPE (OFF por default; baseline byte-identico em comportamento).
Ancora: `ctx->lr = 0x000415A4;` e' unico no lift; o ps3_indirect_call saltado e'
o primeiro que se lhe segue (o unico nas 12 linhas seguintes).
rc: 0 aplicado/ja; 2 se a ancora nao casar exactamente 1x.
"""
import glob, os, sys
MARKER = "E86-KEEP-SCOPE"
ANCH = "        ctx->lr = 0x000415A4; func_000C2E68(ctx); DRAIN_TRAMPOLINE(ctx);\n"
CALL = "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
GATE = ('        /* ' + MARKER + ' */\n'
        '        { static int _e86 = -1; if (_e86 < 0) { extern char* getenv(const char*);\n'
        '              const char* _e = getenv("PS3_E86_KEEP_SCOPE"); _e86 = (_e && *_e && *_e != \'0\') ? 1 : 0;\n'
        '              if (_e86) { fprintf(stderr, "[E86] KEEP_SCOPE ligado: o pop-de-tudo de FUN_00041494 (lr=0x000415A4) vai ser SALTADO\\n"); fflush(stderr); } }\n'
        '          if (_e86) { fprintf(stderr, "[E86] pop-de-tudo saltado (ctr=0x%08X r3=0x%08X)\\n", (uint32_t)ctx->ctr, (uint32_t)ctx->gpr[3]); fflush(stderr); }\n'
        '          else { ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx); } }\n')
def main():
    lift = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "recomp_macos_v2"))
    for path in sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp"))):
        s = open(path, errors="replace").read()
        if MARKER in s: print("ALREADY", os.path.basename(path)); return 0
        n = s.count(ANCH)
        if n == 0: continue
        if n != 1: print("ERRO: ancora casou %d vezes" % n, file=sys.stderr); return 2
        i = s.index(ANCH) + len(ANCH); j = s.index(CALL, i)
        if s.count(CALL, i, j + len(CALL)) != 1 or j - i > 700:
            print("ERRO: o icall seguinte nao esta onde devia", file=sys.stderr); return 2
        s = s[:j] + GATE + s[j + len(CALL):]
        open(path, "w").write(s); print("APPLIED", os.path.basename(path)); return 0
    print("MISSING", file=sys.stderr); return 2
if __name__ == "__main__": sys.exit(main())
