#!/usr/bin/env python3
"""PUSHH -- por dentro do handler de push dos registos 0x13/0x05 (E87).

Medido: os dois registos 0x13 ('abre grupo') do WAD nao empurram nada (um em
silencio, outro com ICALL-BAD) e os dois 0x06 ('fecha grupo') despejam os 10
gestores -- e' isso que deixa o escopo em -1 no #042. O handler FUN_002b0db0:
  obj  = FUN_002aac5c(FUN_002aab64(), hdr+8)     lookup do objecto do grupo
  mgr  = TABLE[*(u16*)(obj+2)]                    gestor da classe
  prod = mgr->vt[5](mgr, obj)                     regista no escopo corrente (precisa de escopo!)
  h    = prod ? prod-4 : 0
  mgr2 = TABLE[*(u16*)(h+6)]; mgr2->vt[0x40](mgr2, h+4)   push
Tres pontos, todos a capturar registos VIVOS no fluxo. O cursor do gestor
(int8 em mgr+0xC8) e' uma leitura directa, rotulada como tal -- e' o estado que
discrimina 'escopo vazio' de 'produto sem slot'. Gate PS3_TRACE_PUSHH.
"""
import glob, os, sys
MARKER = "PUSHH-PROBE"
def g(tag, fmt, args):
    return ('        /* ' + MARKER + ' ' + tag + ' */\n'
            '        { static int _p_on = -1; if (_p_on < 0) { extern char* getenv(const char*);\n'
            '              const char* _e = getenv("PS3_TRACE_PUSHH"); _p_on = (_e && *_e && *_e != \'0\') ? 1 : 0; }\n'
            '          if (_p_on) { fprintf(stderr, "[PUSHH] ' + tag + ' ' + fmt + '\\n", ' + args + '); fflush(stderr); } }\n')
N1 = "        ctx->lr = 0x002B0DDC; func_002AAC5C(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n"
N2 = "        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x14);\n        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]); DRAIN_TRAMPOLINE(ctx);\n        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n"
N3 = "        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x40);\n        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]); DRAIN_TRAMPOLINE(ctx);\n"
P1 = g("lookup", "obj=0x%08X classidx=0x%04X w0=0x%08X", "(uint32_t)ctx->gpr[3], vm_read16(ctx->gpr[3] + 2), vm_read32(ctx->gpr[3])")
P2a = g("pre-vt5", "mgr=0x%08X cursor(directo,+0xC8)=%d opd=0x%08X", "(uint32_t)ctx->gpr[11], (int)(int8_t)vm_read8(ctx->gpr[11] + 0xC8), (uint32_t)ctx->gpr[10]")
P2b = g("pos-vt5", "prod=0x%08X", "(uint32_t)ctx->gpr[3]")
P3 = g("pre-push", "h+4=0x%08X mgr2=0x%08X opd=0x%08X", "(uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[11], (uint32_t)ctx->gpr[10]")
def main():
    lift = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "recomp_macos_v2"))
    for path in sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp"))):
        s = open(path, errors="replace").read()
        if MARKER in s: print("ALREADY"); return 0
        h = "void func_002B0DB0(ppu_context* ctx) {\n"
        if h not in s: continue
        a = s.index(h); b = s.index("\n}\n", a) + 3; body = s[a:b]
        for n in (N1, N2, N3):
            if body.count(n) != 1: print("ERRO needle x%d: %r" % (body.count(n), n[:50]), file=sys.stderr); return 2
        body = body.replace(N1, N1 + P1, 1)
        k = body.index(N2); body = body[:k] + N2.replace("        ps3_call_opd", P2a + "        ps3_call_opd", 1) + P2b + body[k+len(N2):]
        k = body.index(N3); body = body[:k] + N3.replace("        ps3_call_opd", P3 + "        ps3_call_opd", 1) + body[k+len(N3):]
        open(path, "w").write(s[:a] + body + s[b:]); print("APPLIED", os.path.basename(path)); return 0
    print("MISSING", file=sys.stderr); return 2
if __name__ == "__main__": sys.exit(main())
