#!/usr/bin/env python3
"""E87 -- sondas do ciclo de vida do WAD R_LglScA (todas atras de PS3_TRACE_LGLSC).

Regulariza em script idempotente as sondas que a sessao de 2026-09-01 injectou
inline no lift (regra do CLAUDE.md: toda a sonda e' script commitado). Cada bloco
e' inserido na ENTRADA da funcao respectiva, ancorado na assinatura `void
func_X(ppu_context* ctx) {`, que e' unica por simbolo. Marcadores por bloco;
reaplicar e' no-op.

  LGLSC   func_000415F0  finalizacao por nome (p, classidx, ownerslot, flags, bit4)
  D9D0    func_0039D9D0  notify-owner: mgr/obj/flags + 5 frames do host
  CA14    func_0042CA14  release da classe WAD: walker/obj/refcnt(+0x2c)/lr + 4 frames
  REL     func_00255368 e func_0039E15C  release(walker,obj): refcnt(+0x74), lr, 3 frames
  E04C    func_0039E04C  acquire-parent: obj/+0x18/+0x14 -> addref ou nao
  POPH    func_002B0D60  handler 'fecha grupo': classe do payload

Nota honesta: a v2 do D9D0 (ctr dos dois despachos) e a sonda TAB4 foram
instaladas por outros scripts/inline e NAO estao aqui; o lift de producao
ainda as tem. Gate: PS3_TRACE_LGLSC (OFF por default).
"""
import glob, os, sys

GATE = ('        { static int _%(v)s=-1; if(_%(v)s<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_LGLSC"); _%(v)s=(_e&&*_e&&*_e!=\'0\')?1:0; }\n'
        '          if(_%(v)s){ const char* ps3_dbg_sym(void*); %(body)s fflush(stderr);} }\n')

def blk(marker, var, body):
    return "        /* %s */\n" % marker + GATE % {"v": var, "body": body}

SITES = {
 "func_000415F0": blk("LGLSC-PROBE", "l_on",
   'uint32_t _p=(uint32_t)ctx->gpr[3]; uint16_t _fl=vm_read16(_p+8); (void)ps3_dbg_sym;\n'
   '            fprintf(stderr,"[LGLSC] entra func_000415F0 p=0x%08X classidx(+6)=0x%04X ownerslot(+0xA)=0x%04X flags(+8)=0x%04X bit4=%s -> %s lr=0x%08X\\n", _p, vm_read16(_p+6), vm_read16(_p+0xA), _fl, (_fl&4)?"SET":"clear", (_fl&4)?"SALTA o push":"vai empurrar", (uint32_t)ctx->lr);'),
 "func_0039D9D0": blk("D9D0-PROBE", "z_on",
   'uint32_t _o=(uint32_t)ctx->gpr[4];\n'
   '            fprintf(stderr,"[D9D0] mgr=0x%08X obj=0x%08X obj-4->+0x74(refcnt)=%d flags=0x%04X lr=0x%08X ra1=%s ra2=%s ra3=%s ra4=%s ra5=%s\\n", (uint32_t)ctx->gpr[3], _o, (int)vm_read32(_o-4+0x74), vm_read16(_o+4), (uint32_t)ctx->lr, ps3_dbg_sym(__builtin_return_address(0)), ps3_dbg_sym(__builtin_return_address(1)), ps3_dbg_sym(__builtin_return_address(2)), ps3_dbg_sym(__builtin_return_address(3)), ps3_dbg_sym(__builtin_return_address(4)));'),
 "func_0042CA14": blk("CA14-PROBE", "c_on",
   'uint32_t _o=(uint32_t)ctx->gpr[4];\n'
   '            fprintf(stderr,"[CA14] release-WAD walker=0x%08X obj=0x%08X w0=0x%08X refcnt(+0x2c)=%d guest_lr=0x%08X ra1=%s ra2=%s ra3=%s ra4=%s\\n", (uint32_t)ctx->gpr[3], _o, _o?vm_read32(_o-4):0, _o?(int)vm_read32(_o-4+0x2c):-999, (uint32_t)ctx->lr, ps3_dbg_sym(__builtin_return_address(0)), ps3_dbg_sym(__builtin_return_address(1)), ps3_dbg_sym(__builtin_return_address(2)), ps3_dbg_sym(__builtin_return_address(3)));'),
 "func_00255368": blk("REL-PROBE REL-walker", "r_on",
   'uint32_t _o=(uint32_t)ctx->gpr[4];\n'
   '            fprintf(stderr,"[REL-walker] walker=0x%08X obj=0x%08X refcnt(obj-4+0x74)=%d guest_lr=0x%08X ra1=%s ra2=%s ra3=%s\\n", (uint32_t)ctx->gpr[3], _o, _o?(int)vm_read32(_o-4+0x74):-999, (uint32_t)ctx->lr, ps3_dbg_sym(__builtin_return_address(0)), ps3_dbg_sym(__builtin_return_address(1)), ps3_dbg_sym(__builtin_return_address(2)));'),
 "func_0039E15C": blk("REL-PROBE REL-mgr", "r_on",
   'uint32_t _o=(uint32_t)ctx->gpr[4];\n'
   '            fprintf(stderr,"[REL-mgr] walker=0x%08X obj=0x%08X refcnt(obj-4+0x74)=%d guest_lr=0x%08X ra1=%s ra2=%s ra3=%s\\n", (uint32_t)ctx->gpr[3], _o, _o?(int)vm_read32(_o-4+0x74):-999, (uint32_t)ctx->lr, ps3_dbg_sym(__builtin_return_address(0)), ps3_dbg_sym(__builtin_return_address(1)), ps3_dbg_sym(__builtin_return_address(2)));'),
 "func_0039E04C": blk("E04C-PROBE", "a_on",
   'static unsigned long _a_n=0; uint32_t _o=(uint32_t)ctx->gpr[4]; uint32_t _par=_o?vm_read32(_o+0x18):0; uint32_t _f14=_o?vm_read32(_o+0x14):0; _a_n++;\n'
   '            if(_par==0x42F84EA0u || _par==0x42F84EA4u || _a_n<=40 || (_a_n%500)==0)\n'
   '              fprintf(stderr,"[ACQ] #%lu obj=0x%08X parent(+0x18)=0x%08X +0x14=0x%08X -> %s lr=0x%08X ra1=%s ra2=%s\\n", _a_n, _o, _par, _f14, (_par&&!_f14)?"ADDREF":"nao", (uint32_t)ctx->lr, ps3_dbg_sym(__builtin_return_address(0)), ps3_dbg_sym(__builtin_return_address(1)));'),
 "func_002B0D60": blk("POPH-PROBE", "q_on",
   '(void)ps3_dbg_sym; fprintf(stderr,"[POPH] payload=0x%08X w0=0x%08X classidx(+2)=0x%04X +6=0x%04X\\n", (uint32_t)ctx->gpr[4], vm_read32(ctx->gpr[4]), vm_read16(ctx->gpr[4]+2), vm_read16(ctx->gpr[4]+6));'),
}

def main():
    lift = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "recomp_macos_v2"))
    n_new = n_old = 0
    for path in sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp"))):
        s = open(path, errors="replace").read(); t = s
        for fn, code in SITES.items():
            h = "void %s(ppu_context* ctx) {\n" % fn
            if h not in t: continue
            marker = code.split("*/")[0].split("/*")[1].strip()
            a = t.index(h)
            if marker in t[a:a+4000]: n_old += 1; continue
            t = t[:a+len(h)] + code + t[a+len(h):]; n_new += 1
        if t != s: open(path, "w").write(t)
    print("e87 probes: novas=%d ja=%d (esperadas %d)" % (n_new, n_old, len(SITES)))
    return 0 if n_new + n_old == len(SITES) else 2

if __name__ == "__main__": sys.exit(main())
