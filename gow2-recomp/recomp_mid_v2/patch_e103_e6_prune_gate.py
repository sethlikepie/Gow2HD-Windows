#!/usr/bin/env python3
r"""E103 -- a poda E6 (PAREDE1-E6-SUBTAG-SKIP-FIX, patch_24e1e8_wall1_subtag_skip.py) passa a OFF por default.

Medido (E102, 2026-09-02): a poda salta o dispatch (attach vt[0x18]) de objectos com w0=0x40030001
(subtag 3, low16 1) -- entre eles 0x407C7670, que e' exactamente o parent que func_0009EC0C usa
depois do R_Hero01 para ir buscar o filho de classe 15 em parent+0xB8+idx*4; como o parent nunca foi
despachado, o slot tem lixo (float 1.0f), FUN_00283328 recebe 0x3F7FFFEC e func_002DAF04/002DACCC
percorrem listas a partir de 0x1030/0x2410 para sempre. O console despacha esses objectos normalmente.
PS3_E6_PRUNE=1 repoe a poda so' para A/B. Idempotente (marcador E103-E6-GATE). Depende de
patch_24e1e8_wall1_subtag_skip.py (escreve os blocos que este gate altera)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
OLD = "                if (_e6low16 == 1u && _e6subtag != 1u) {\n                    ps3_e6w1_skip = true;\n"
NEW = "                if (_e6low16 == 1u && _e6subtag != 1u && e6_prune_on()) { /* E103-E6-GATE */\n                    ps3_e6w1_skip = true;\n"
HELPER = '''/* E103 (2026-09-02): the E6 prune is OFF by default; PS3_E6_PRUNE=1 restores it for A/B. */
static int e6_prune_on(void) {
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_E6_PRUNE"); on = (e && *e && *e != '0') ? 1 : 0; }
    return on;
}
'''
def main():
    tot=0
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace'); n=s.count(OLD)
        if not n and "E103-E6-GATE" not in s: continue
        if "E103-E6-GATE" in s: print(f"E103: ALREADY em {f.name}"); tot+=1; continue
        anchor="void func_"; i=s.find("\nvoid func_")
        s=s[:i+1]+HELPER+s[i+1:]
        s=s.replace(OLD,NEW); f.write_text(s); print(f"E103: {n} sitios em {f.name}"); tot+=n
    return 0 if tot else 2
if __name__=="__main__": raise SystemExit(main())
