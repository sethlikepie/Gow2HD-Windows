#!/usr/bin/env python3
"""TAB4 -- H1 (TOC errado no chamador) vs H2 (TABLE[4] zerado por escrita host).

Medido (2026-09-01, watch em 0x868D58): TABLE[4] := 0x400D6808 na linha 367 do
log; os 8 despachos com r11=0 acontecem na 4377. O lift em func_000C992C le'
`r31 = *(r2 - 0x5EF0)` com o r2 VIVO e depois `r11 = *(r31 + 0x10)`. Ou o r2
esta' errado (H1) e r31 e' lixo, ou TABLE[4] foi zerado sem passar por
vm_write* (H2) -- ja aconteceu com 0x400C3D88 (nota no ppu_loader.cpp).

A sonda captura r2 ANTES do load e r31/r11 DEPOIS (vivos, na ordem do fluxo),
e le' *(0x868D58) directamente como CONTROLE ao lado do r11 capturado: se r31 ==
0x868D48 e r11 != mem, a sonda esta' partida; se r31 != 0x868D48, e' H1; se r31
== 0x868D48 e r11 == mem == 0, e' H2.

Gate PS3_TRACE_TAB4 (OFF por default). Sem cap. Needle unico no lift (-0x5EF0).
"""
import glob, os, sys
MARKER = "TAB4-PROBE"
N1 = "        ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x5EF0);\n"
N2 = "        ctx->gpr[11] = vm_read32(ctx->gpr[31] + 0x10);\n"
PRE = ('        /* ' + MARKER + ' */\n'
       '        static int _t4_on = -1; if (_t4_on < 0) { extern char* getenv(const char*);\n'
       '            const char* _e = getenv("PS3_TRACE_TAB4"); _t4_on = (_e && *_e && *_e != \'0\') ? 1 : 0; }\n'
       '        uint32_t _t4_r2 = (uint32_t)ctx->gpr[2];\n')
POST = ('        if (_t4_on) { fprintf(stderr, "[TAB4] r2_antes=0x%08X r31=0x%08X r11=0x%08X "\n'
        '                "mem[0x868D58]=0x%08X lr=0x%08X CTRL=%s\\n", _t4_r2, (uint32_t)ctx->gpr[31],\n'
        '                (uint32_t)ctx->gpr[11], vm_read32(0x00868D58u), (uint32_t)ctx->lr,\n'
        '                (_t4_r2 == 0x00541178u) ? "TOC-OK" : "TOC-BAD"); fflush(stderr); }\n')
def main():
    lift = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "recomp_macos_v2"))
    for path in sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp"))):
        s = open(path, errors="replace").read()
        if MARKER in s: print("ALREADY", os.path.basename(path)); return 0
        if s.count(N1) != 1: continue
        i = s.index(N1); j = s.index(N2, i)
        if j - i > 400: print("ERRO: N2 longe de N1", file=sys.stderr); return 2
        s = s[:i] + PRE + N1 + s[i+len(N1):j] + N2 + POST + s[j+len(N2):]
        # v2: o ctr do despacho que se segue (o metodo vt[5] do gestor de tipo 4).
        # Capturado no ponto em que o lift acabou de o calcular, antes do icall.
        N3 = "        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);\n        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        k = s.index(N3, s.index(POST))
        if k - s.index(POST) > 800: print("ERRO: N3 longe", file=sys.stderr); return 2
        CTR = ('        if (_t4_on) { fprintf(stderr, "[TAB4] despacho vt[5]: obj=0x%08X vtable=0x%08X opd=0x%08X ctr=0x%08X\\n",\n'
               '                (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[9], (uint32_t)ctx->gpr[10], (uint32_t)ctx->ctr); fflush(stderr); }\n')
        s = s[:k] + "        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);\n" + CTR + "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n" + s[k+len(N3):]
        open(path, "w").write(s); print("APPLIED", os.path.basename(path)); return 0
    print("MISSING", file=sys.stderr); return 2
if __name__ == "__main__": sys.exit(main())
